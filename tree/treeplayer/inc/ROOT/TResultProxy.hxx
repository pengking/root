// Author: Enrico Guiraud, Danilo Piparo CERN  03/2017

/*************************************************************************
 * Copyright (C) 1995-2016, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TRESULTPROXY
#define ROOT_TRESULTPROXY

#include "ROOT/TypeTraits.hxx"
#include "ROOT/TDFNodes.hxx"
#include "TError.h" // Warning

#include <memory>
#include <functional>

namespace ROOT {

namespace Experimental {
namespace TDF {
// Fwd decl for MakeResultProxy
template <typename T>
class TResultProxy;
}
}

namespace Detail {
namespace TDF {
using ROOT::Experimental::TDF::TResultProxy;
// Fwd decl for TResultProxy
template <typename T>
TResultProxy<T> MakeResultProxy(const std::shared_ptr<T> &r, const std::shared_ptr<TLoopManager> &df,
                                TDFInternal::TActionBase *actionPtr = nullptr);
} // ns TDF
} // ns Detail

namespace Experimental {
namespace TDF {
namespace TDFInternal = ROOT::Internal::TDF;
namespace TDFDetail = ROOT::Detail::TDF;
namespace TTraits = ROOT::TypeTraits;

/// Smart pointer for the return type of actions
/**
\class ROOT::Experimental::TDF::TResultProxy
\ingroup dataframe
\brief A wrapper around the result of TDataFrame actions able to trigger calculations lazily.
\tparam T Type of the action result

A smart pointer which allows to access the result of a TDataFrame action. The
methods of the encapsulated object can be accessed via the arrow operator.
Upon invocation of the arrow operator or dereferencing (`operator*`), the
loop on the events and calculations of all scheduled actions are executed
if needed.
It is possible to iterate on the result proxy if the proxied object is a collection.
~~~{.cpp}
for (auto& myItem : myResultProxy) { ... };
~~~
If iteration is not supported by the type of the proxied object, a compilation error is thrown.

*/
template <typename T>
class TResultProxy {
   /// \cond HIDDEN_SYMBOLS
   template <typename V, bool isCont = TTraits::IsContainer<V>::value>
   struct TIterationHelper {
      using Iterator_t = void;
      void GetBegin(const V &) { static_assert(sizeof(V) == 0, "It does not make sense to ask begin for this class."); }
      void GetEnd(const V &) { static_assert(sizeof(V) == 0, "It does not make sense to ask end for this class."); }
   };

   template <typename V>
   struct TIterationHelper<V, true> {
      using Iterator_t = decltype(std::begin(std::declval<V>()));
      static Iterator_t GetBegin(const V &v) { return std::begin(v); };
      static Iterator_t GetEnd(const V &v) { return std::end(v); };
   };
   /// \endcond
   using SPT_t = std::shared_ptr<T>;
   using SPTLM_t = std::shared_ptr<TDFDetail::TLoopManager>;
   using WPTLM_t = std::weak_ptr<TDFDetail::TLoopManager>;
   using ShrdPtrBool_t = std::shared_ptr<bool>;
   template <typename W>
   friend TResultProxy<W>
   TDFDetail::MakeResultProxy(const std::shared_ptr<W> &, const SPTLM_t &, TDFInternal::TActionBase *);

   const ShrdPtrBool_t fReadiness =
      std::make_shared<bool>(false); ///< State registered also in the TLoopManager until the event loop is executed
   WPTLM_t fImplWeakPtr;             ///< Points to the TLoopManager at the root of the functional graph
   const SPT_t fObjPtr;              ///< Shared pointer encapsulating the wrapped result
   TDFInternal::TActionBase *fActionPtr = nullptr; ///< Points to the TDF action that produces this result

   /// Triggers the event loop in the TLoopManager instance to which it's associated via the fImplWeakPtr
   void TriggerRun();

   /// Get the pointer to the encapsulated result.
   /// Ownership is not transferred to the caller.
   /// Triggers event loop and execution of all actions booked in the associated TLoopManager.
   T *Get()
   {
      if (!*fReadiness)
         TriggerRun();
      return fObjPtr.get();
   }

   TResultProxy(const SPT_t &objPtr, const ShrdPtrBool_t &readiness, const SPTLM_t &loopManager,
                TDFInternal::TActionBase *actionPtr = nullptr)
      : fReadiness(readiness), fImplWeakPtr(loopManager), fObjPtr(objPtr), fActionPtr(actionPtr)
   {
   }

   void SetActionPtr(TDFInternal::TActionBase *a) { fActionPtr = a; }

   void RegisterCallback(ULong64_t everyNEvents, std::function<void(unsigned int)> &&c) {
      // TODO remove once useless: at the time of writing jitted actions, Reduce, Take, Count are missing the feature
      if (!fActionPtr)
         throw std::runtime_error("Callback registration not implemented for this kind of action yet.");
      auto lm = fImplWeakPtr.lock();
      if (!lm)
         throw std::runtime_error("The main TDataFrame is not reachable: did it go out of scope?");
      lm->RegisterCallback(everyNEvents, std::move(c));
   }

public:
   using Value_t = T; ///< Convenience alias to simplify access to proxied type
   static constexpr ULong64_t kOnce = 0ull; ///< Convenience definition to express a callback must be executed once

   TResultProxy() = delete;

   /// Get a const reference to the encapsulated object.
   /// Triggers event loop and execution of all actions booked in the associated TLoopManager.
   const T &GetValue() { return *Get(); }

   /// Get a pointer to the encapsulated object.
   /// Triggers event loop and execution of all actions booked in the associated TLoopManager.
   T &operator*() { return *Get(); }

   /// Get a pointer to the encapsulated object.
   /// Ownership is not transferred to the caller.
   /// Triggers event loop and execution of all actions booked in the associated TLoopManager.
   T *operator->() { return Get(); }

   /// Return an iterator to the beginning of the contained object if this makes
   /// sense, throw a compilation error otherwise
   typename TIterationHelper<T>::Iterator_t begin()
   {
      if (!*fReadiness)
         TriggerRun();
      return TIterationHelper<T>::GetBegin(*fObjPtr);
   }

   /// Return an iterator to the end of the contained object if this makes
   /// sense, throw a compilation error otherwise
   typename TIterationHelper<T>::Iterator_t end()
   {
      if (!*fReadiness)
         TriggerRun();
      return TIterationHelper<T>::GetEnd(*fObjPtr);
   }

   /// Register a callback that TDataFrame will execute "everyNEvents" on a partial result.
   ///
   /// \param[in] everyNEvents Frequency at which the callback will be called, as a number of events processed
   /// \param[in] callback a callable with signature `void(Value_t&)` where Value_t is the type of the value contained in this TResultProxy
   ///
   /// The callback must be a callable (lambda, function, functor class...) that takes a reference to the result type as
   /// argument and returns nothing. TDataFrame will invoke registered callbacks passing partial action results as
   /// arguments to them (e.g. a histogram filled with a part of the selected events, a counter incremented only up to a
   /// certain point, a mean over a subset of the events and so forth).
   ///
   /// Callbacks can be used e.g. to inspect partial results of the analysis while the event loop is running. For
   /// example one can draw an up-to-date version of a result histogram every 100 entries like this:
   /// \code{.cpp}
   /// auto h = tdf.Histo1D("x");
   /// TCanvas c("c","x hist");
   /// h.OnPartialResult(100, [&c](TH1D &h_) { c.cd(); h_.Draw(); c.Update(); });
   /// h->Draw(); // event loop runs here, this `Draw` is executed after the event loop is finished
   /// \endcode
   ///
   /// A value of 0 for everyNEvents indicates the callback must be executed only once, before running the event loop.
   /// A conveniece definition `kOnce` is provided to make this fact more expressive in user code (see snippet below).
   /// Multiple callbacks can be registered with the same TResultProxy (i.e. results of TDataFrame actions) and will
   /// be executed sequentially. Callbacks are executed in the order they were registered.
   /// The type of the value contained in a TResultProxy is also available as TResultProxy<T>::Value_t, e.g.
   /// \code{.cpp}
   /// auto h = tdf.Histo1D("x");
   /// // h.kOnce is 0
   /// // decltype(h)::Value_t is TH1D
   /// \endcode
   ///
   /// When implicit multi-threading is enabled, the callback:
   /// - will never be executed by multiple threads concurrently: it needs not be thread-safe. For example the snippet
   ///   above that draws the partial histogram on a canvas works seamlessly in multi-thread event loops.
   /// - will always be executed "everyNEvents": partial results will "contain" that number of events more from
   ///   one call to the next
   /// - might be executed by a different worker thread at different times: the value of `std::this_thread::get_id()`
   ///   might change between calls
   /// To register a callback that is called by _each_ worker thread (concurrently) every N events one can use
   /// OnPartialResultSlot.
   void OnPartialResult(ULong64_t everyNEvents, std::function<void(T&)> callback)
   {
      auto actionPtr = fActionPtr; // only variables with automatic storage duration can be captured
      auto c = [actionPtr, callback](unsigned int slot) {
         if (slot != 0)
            return;
         auto partialResult = static_cast<Value_t*>(actionPtr->PartialUpdate(slot));
         callback(*partialResult);
      };
      RegisterCallback(everyNEvents, std::move(c));
   }

   /// Register a callback that TDataFrame will execute in each worker thread concurrently on that thread's partial result.
   ///
   /// \param[in] everyNEvents Frequency at which the callback will be called by each thread, as a number of events processed
   /// \param[in] a callable with signature `void(unsigned int, Value_t&)` where Value_t is the type of the value contained in this TResultProxy
   ///
   /// See `RegisterCallback` for a generic explanation of the callback mechanism.
   /// Compared to `RegisterCallback`, this method has two major differences:
   /// - all worker threads invoke the callback once every specified number of events. The event count is per-thread,
   ///   and callback invocation might happen concurrently (i.e. the callback must be thread-safe)
   /// - the callable must take an extra `unsigned int` parameter corresponding to a multi-thread "processing slot":
   ///   this is a "helper value" to simplify writing thread-safe callbacks: different worker threads might invoke the
   ///   callback concurrently but always with different `slot` numbers.
   /// - a value of 0 for everyNEvents indicates the callback must be executed once _per slot_.
   ///
   /// For example, the following snippet prints out a thread-safe progress bar of the events processed by TDataFrame
   /// \code
   /// auto c = tdf.Count(); // any action would do, but `Count` is the most lightweight
   /// std::string progress;
   /// std::mutex bar_mutex;
   /// c.OnPartialResultSlot(nEvents / 100, [&progress, &bar_mutex](unsigned int, ULong64_t &) {
   ///    std::lock_guard<std::mutex> lg(bar_mutex);
   ///    progress.push_back('#');
   ///    std::cout << "\r[" << std::left << std::setw(100) << progress << ']' << std::flush;
   /// });
   /// std::cout << "Analysis running..." << std::endl;
   /// *c; // trigger the event loop by accessing an action's result
   /// std::cout << "\nDone!" << std::endl;
   /// \endcode
   void OnPartialResultSlot(ULong64_t everyNEvents, std::function<void(unsigned int, T&)> callback)
   {
      auto actionPtr = fActionPtr;
      auto c = [actionPtr, callback](unsigned int slot) {
         auto partialResult = static_cast<Value_t*>(actionPtr->PartialUpdate(slot));
         callback(slot, *partialResult);
      };
      RegisterCallback(everyNEvents, std::move(c));
   }
};

template <typename T>
void TResultProxy<T>::TriggerRun()
{
   auto df = fImplWeakPtr.lock();
   if (!df) {
      throw std::runtime_error("The main TDataFrame is not reachable: did it go out of scope?");
   }
   df->Run();
}
} // end NS TDF
} // end NS Experimental

namespace Detail {
namespace TDF {
template <typename T>
TResultProxy<T> MakeResultProxy(const std::shared_ptr<T> &r, const std::shared_ptr<TLoopManager> &df,
                                TDFInternal::TActionBase *actionPtr)
{
   auto readiness = std::make_shared<bool>(false);
   auto resPtr = TResultProxy<T>(r, readiness, df, actionPtr);
   df->Book(readiness);
   return resPtr;
}
} // end NS TDF
} // end NS Detail
} // end NS ROOT

#endif // ROOT_TRESULTPROXY
